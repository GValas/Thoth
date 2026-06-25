import { BadRequestException, Body, Controller, Post } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import {
  IsArray,
  IsBoolean,
  IsIn,
  IsObject,
  IsOptional,
  IsString,
  Matches,
} from 'class-validator';
import { EngineError } from '@thoth/shared';
import { InstrumentService, type InstrumentPriceDto } from './instrument.service';

class InstrumentDto implements InstrumentPriceDto {
  @IsString() workspaceId!: string;
  @IsIn(['ana', 'pde', 'mcl', 'mcl_gpu']) engine!: 'ana' | 'pde' | 'mcl' | 'mcl_gpu';
  @IsIn(['vanilla', 'barrier', 'variance_swap']) kind!: string;
  //! free-form instrument fields — the engine validates the actual kind, so any variation
  //! it accepts (barrier_type, monitoring, nominal, is_absolute_strike, …) flows through.
  @IsObject() instrument!: Record<string, unknown>;
  @IsArray() @IsString({ each: true }) indicators!: string[];
  @IsOptional() @IsString() currency?: string;
  @IsOptional() @Matches(/^\d{4}-\d{2}-\d{2}$/) today?: string;
  @IsOptional() @IsBoolean() live?: boolean;
  @IsOptional() @IsString() configName?: string;
  @IsOptional() @IsString() correlationName?: string;
}

@ApiTags('instrument')
@Controller('instrument')
export class InstrumentController {
  constructor(private readonly instrument: InstrumentService) {}

  //! Price one hand-entered instrument synchronously (premium + Greeks). The frontend's
  //! pricing panels and the monitoring blotter call this; `live: true` overlays live spots.
  @Post('price')
  async price(@Body() dto: InstrumentDto) {
    try {
      return await this.instrument.price(dto);
    } catch (e) {
      // an engine-reported failure (bad/unpriceable product) is the caller's fault → 400.
      if (e instanceof EngineError) throw new BadRequestException(e.message);
      throw e;
    }
  }
}
