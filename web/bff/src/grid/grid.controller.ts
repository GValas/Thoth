import { Body, Controller, Get, Param, Post } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import {
  ArrayMaxSize,
  ArrayNotEmpty,
  IsArray,
  IsIn,
  IsNumber,
  IsOptional,
  IsString,
  Matches,
} from 'class-validator';
import { CurrentUser, type AuthUser } from '../common/decorators';
import { GridService, type GridSubmitDto } from './grid.service';

class GridDto implements GridSubmitDto {
  @IsString() workspaceId!: string;
  @IsIn(['ana', 'pde', 'mcl', 'mcl_gpu']) engine!: 'ana' | 'pde' | 'mcl' | 'mcl_gpu';
  //! Per-axis size caps: bound the request BEFORE it fans out into cells so a single
  //! payload cannot enqueue an unbounded pricing job (the total cell count is capped in
  //! GridService.submit as well — belt and braces).
  @IsArray() @ArrayNotEmpty() @ArrayMaxSize(20) @IsString({ each: true }) underlyings!: string[];
  @IsArray() @ArrayNotEmpty() @ArrayMaxSize(8) @IsIn(['call', 'put'], { each: true }) types!: ('call' | 'put')[];
  @IsArray() @ArrayNotEmpty() @ArrayMaxSize(200) @IsNumber({}, { each: true }) strikes!: number[];
  @IsArray() @ArrayNotEmpty() @ArrayMaxSize(120) @Matches(/^\d{4}-\d{2}-\d{2}$/, { each: true }) maturities!: string[];
  @IsArray() @IsString({ each: true }) indicators!: string[];
  @IsOptional() @IsIn(['european', 'american']) exercise?: 'european' | 'american';
  @IsOptional() @IsString() configName?: string;
  @IsOptional() @IsString() correlationName?: string;
  @IsOptional() @Matches(/^\d{4}-\d{2}-\d{2}$/) today?: string;
  @IsOptional() @IsString() currency?: string;
}

@ApiTags('grid')
@Controller('grid')
export class GridController {
  constructor(private readonly grid: GridService) {}

  //! enqueue a grid run; returns { jobId } to poll
  @Post()
  submit(@Body() dto: GridDto, @CurrentUser() user: AuthUser) {
    return this.grid.submit(dto, user);
  }

  //! live re-price: synchronous, live spots overlaid, returns { matrices, meta } directly
  //! (no job). The frontend Live mode calls this on a throttle.
  @Post('live')
  priceLive(@Body() dto: GridDto, @CurrentUser() user: AuthUser) {
    return this.grid.priceLive(dto, user);
  }

  @Get(':id')
  get(@Param('id') id: string, @CurrentUser() user: AuthUser) {
    return this.grid.get(id, user);
  }

  @Get(':id/progress')
  progress(@Param('id') id: string, @CurrentUser() user: AuthUser) {
    return this.grid.progress(id, user);
  }
}
