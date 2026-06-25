import { Body, Controller, Get, Param, Post } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import {
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
  @IsArray() @ArrayNotEmpty() @IsString({ each: true }) underlyings!: string[];
  @IsArray() @ArrayNotEmpty() @IsIn(['call', 'put'], { each: true }) types!: ('call' | 'put')[];
  @IsArray() @ArrayNotEmpty() @IsNumber({}, { each: true }) strikes!: number[];
  @IsArray() @ArrayNotEmpty() @Matches(/^\d{4}-\d{2}-\d{2}$/, { each: true }) maturities!: string[];
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
    return this.grid.submit(dto, user.userId);
  }

  @Get(':id')
  get(@Param('id') id: string) {
    return this.grid.get(id);
  }

  @Get(':id/progress')
  progress(@Param('id') id: string) {
    return this.grid.progress(id);
  }
}
